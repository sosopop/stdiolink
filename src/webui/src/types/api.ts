export interface PaginatedResponse<T> {
  [key: string]: T[] | number;
  total: number;
  page: number;
  pageSize: number;
}

export interface ApiError {
  error: string;
  status: number;
}
